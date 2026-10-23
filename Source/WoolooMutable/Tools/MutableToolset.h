#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooMutable/Tools/MutableToolResults.h"
#include "MutableToolset.generated.h"

/** Inspect and author Mutable graphs. Writes require an explicit directory, metadata key, and owner. */
UCLASS(BlueprintType, Hidden)
class WOOLOOMUTABLE_API UMutableToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** List supported Mutable node classes and their accepted property names/types/defaults. */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static TArray<FMutableToolNodeType> ListNodeTypes();

    /**
     * Inspect nodes, editable properties, pins and connections without modifying the asset.
     * @param AssetPath Long package path, e.g. /Game/Characters/Example/CO_Example, without an object suffix.
     * @param OwnerMetadataKey Caller-selected metadata key whose value is returned as Owner.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolGraphResult DescribeObject(const FString& AssetPath, const FString& OwnerMetadataKey);

    /**
     * Create an unsaved owned CO with Mutable's native factory. Existing matching assets are reused, never adopted.
     * @param AssetPath New long package path without an object suffix.
     * @param AllowedPackageRoot Writable content directory containing AssetPath.
     * @param OwnerMetadataKey Caller-selected metadata key used to store and check ownership.
     * @param ExpectedOwner Non-empty builder identifier stored only on newly created assets.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolResult CreateObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                           const FString& OwnerMetadataKey, const FString& ExpectedOwner);

    /**
     * Replace the complete graph of an owned CO. Does not save or compile. Omitted nodes/links are removed.
     * @param AssetPath Target long package path without an object suffix.
     * @param AllowedPackageRoot Explicit writable directory boundary.
     * @param OwnerMetadataKey Caller-selected metadata key used to check ownership.
     * @param ExpectedOwner Must match the value stored under OwnerMetadataKey on the existing asset.
     * @param Graph Complete specification using ListNodeTypes properties and live pin names. Preserve existing GUIDs.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolGraphResult ApplyGraph(const FString& AssetPath, const FString& AllowedPackageRoot,
                                              const FString& OwnerMetadataKey, const FString& ExpectedOwner,
                                              const FMutableToolGraph& Graph);

    /**
     * Synchronously compile an owned ROOT CO, returning this request's callback flags. No package is saved.
     * Detailed compiler messages remain in LogMutable. Save child COs first so registry discovery can find them.
     * @param AssetPath Root CO long package path without an object suffix; child COs are rejected.
     * @param AllowedPackageRoot Explicit writable directory boundary.
     * @param OwnerMetadataKey Caller-selected metadata key used to check ownership.
     * @param ExpectedOwner Must match the target asset's ownership marker.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolCompileResult CompileObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                                  const FString& OwnerMetadataKey, const FString& ExpectedOwner);

    /**
     * Read compiled parameter names/types, scalar defaults, named options and output components.
     * Fails when compilation is missing/busy, native Mutable reports stale data, or MCP edits still need compiling.
     * @param AssetPath Root CO long package path without an object suffix.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolParametersResult GetParameters(const FString& AssetPath);

    /**
     * Save ONLY the specified owned CO package. Does not save dependencies or perform source-control operations.
     * @param AssetPath Target long package path without an object suffix.
     * @param AllowedPackageRoot Explicit writable directory boundary.
     * @param OwnerMetadataKey Caller-selected metadata key used to check ownership.
     * @param ExpectedOwner Must match the target asset's ownership marker.
     */
    UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "Mutable")
    static FMutableToolResult SaveObject(const FString& AssetPath, const FString& AllowedPackageRoot,
                                         const FString& OwnerMetadataKey, const FString& ExpectedOwner);
};
