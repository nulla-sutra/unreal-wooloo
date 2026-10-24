#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "WoolooAssets/Tools/AssetToolTypes.h"
#include "AssetToolset.generated.h"

UCLASS(BlueprintType, Hidden)
class WOOLOOASSETS_API UWoolooAssetToolset : public UToolsetDefinition
{
    GENERATED_BODY()

public:
    /** Read-only preflight of explicit package moves and their disk-registry referencers.
     * Requires narrow writable roots; protected roots are excluded even when nested inside them.
     * Does not load assets, move files, save packages, or fix references. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult PlanMoves(const TArray<FWoolooAssetMove>& Moves,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots);

    /** Execute the reviewed mapping through native AssetTools::RenameAssets.
     * Native rename also fixes references, saves packages, and may use configured source control.
     * ApprovedReferencerPackages must include every referencer returned by a fresh PlanMoves.
     * Refuses PIE, dirty editor packages, conflicts, read-only files and protected/engine content.
     * May partially succeed; inspect CompletedPackages and Issues before retrying. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult MoveAssets(const TArray<FWoolooAssetMove>& Moves,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
        const TArray<FString>& ApprovedReferencerPackages);

    /** Read-only inspection of an explicit list of old package names. Missing packages are ignored.
     * Existing packages must be redirectors within AllowedPackageRoots. Does not repair them. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult InspectRedirectors(const TArray<FString>& RedirectorPackages,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots);

    /** Replace soft paths and resave exact approved referencers through native APIs.
     * Rechecks the approved referencer list and clean editor state before authoring.
     * Deletes redirectors only after disk and memory reference checks, and updates affected collections.
     * Avoids the modal report in AssetTools::FixupReferencers. Never force-deletes files. */
    UFUNCTION(BlueprintCallable, meta=(AICallable), Category="Assets")
    static FWoolooAssetResult FixupRedirectors(const TArray<FString>& RedirectorPackages,
        const TArray<FString>& AllowedPackageRoots, const TArray<FString>& ProtectedPackageRoots,
        const TArray<FString>& ApprovedReferencerPackages);
};
